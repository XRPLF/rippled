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

CallQueue::CallQueue (String name)
    : m_name (name)
{
}

CallQueue::~CallQueue ()
{
    // Someone forget to close the queue.
    bassert (m_closed.isSignaled ());

    // Can't destroy queue with unprocessed calls.
    bassert (m_queue.empty ());
}

bool CallQueue::isAssociatedWithCurrentThread () const
{
    return Thread::getCurrentThreadId () == m_id;
}

// Adds a call to the queue of execution.
void CallQueue::queuep (Work* c)
{
    // If this goes off it means calls are being made after the
    // queue is closed, and probably there is no one around to
    // process it.
    bassert (!m_closed.isSignaled ());

    if (m_queue.push_back (c))
        signal ();
}

// Append the Work to the queue. If this call is made from the same
// thread as the last thread that called synchronize(), then the call
// will execute synchronously.
//
void CallQueue::callp (Work* c)
{
    queuep (c);

    // If we are called on the process thread and we are not
    // recursed into doSynchronize, then process the queue. This
    // makes calls from the process thread synchronous.
    //
    // NOTE: The value of isBeingSynchronized is invalid/volatile unless
    // this thread is the last process thread.
    //
    // NOTE: There is a small window of opportunity where we
    // might get an undesired synchronization if new thread
    // calls synchronize() concurrently.
    //
    if (isAssociatedWithCurrentThread () &&
            m_isBeingSynchronized.trySignal ())
    {
        doSynchronize ();

        m_isBeingSynchronized.reset ();
    }
}

bool CallQueue::synchronize ()
{
    bool did_something;

    // Detect recursion into doSynchronize(), and
    // break ties for concurrent calls atomically.
    //
    if (m_isBeingSynchronized.trySignal ())
    {
        // Remember this thread.
        m_id = Thread::getCurrentThreadId ();

        did_something = doSynchronize ();

        m_isBeingSynchronized.reset ();
    }
    else
    {
        did_something = false;
    }

    return did_something;
}

// Can still have pending calls, just can't put new ones in.
void CallQueue::close ()
{
    m_closed.signal ();

    synchronize ();
}

// Process everything in the queue. The list of pending calls is
// acquired atomically. New calls may enter the queue while we are
// processing.
//
// Returns true if any functors were called.
//
bool CallQueue::doSynchronize ()
{
    bool did_something;

    // Reset since we are emptying the queue. Since we loop
    // until the queue is empty, it is possible for us to exit
    // this function with an empty queue and signaled state.
    //
    reset ();

    Work* call = m_queue.pop_front ();

    if (call)
    {
        did_something = true;

        // This method of processing one at a time has the desired
        // side effect of synchronizing nested calls to us from a functor.
        //
        for (;;)
        {
            call->operator () ();
            delete call;

            call = m_queue.pop_front ();

            if (call == 0)
                break;
        }
    }
    else
    {
        did_something = false;
    }

    return did_something;
}
