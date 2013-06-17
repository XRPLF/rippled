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

ReadWriteMutex::ReadWriteMutex () noexcept
{
}

ReadWriteMutex::~ReadWriteMutex () noexcept
{
}

void ReadWriteMutex::enterRead () const noexcept
{
    for (;;)
    {
        // attempt the lock optimistically
        // THIS IS NOT CACHE-FRIENDLY!
        m_readers->addref ();

        // is there a writer?
        // THIS IS NOT CACHE-FRIENDLY!
        if (m_writes->isSignaled ())
        {
            // a writer exists, give up the read lock
            m_readers->release ();

            // block until the writer is done
            {
                CriticalSection::ScopedLockType lock (m_mutex);
            }

            // now try the loop again
        }
        else
        {
            break;
        }
    }
}

void ReadWriteMutex::exitRead () const noexcept
{
    m_readers->release ();
}

void ReadWriteMutex::enterWrite () const noexcept
{
    // Optimistically acquire the write lock.
    m_writes->addref ();

    // Go for the mutex.
    // Another writer might block us here.
    m_mutex.enter ();

    // Only one competing writer will get here,
    // but we don't know who, so we have to drain
    // readers no matter what. New readers will be
    // blocked by the mutex.
    //
    if (m_readers->isSignaled ())
    {
        SpinDelay delay;

        do
        {
            delay.pause ();
        }
        while (m_readers->isSignaled ());
    }
}

void ReadWriteMutex::exitWrite () const noexcept
{
    // Releasing the mutex first and then decrementing the
    // writer count allows another waiting writer to atomically
    // acquire the lock, thus starving readers. This fulfills
    // the write-preferencing requirement.

    m_mutex.exit ();

    m_writes->release ();
}
