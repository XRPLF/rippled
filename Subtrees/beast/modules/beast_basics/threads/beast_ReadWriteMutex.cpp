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
