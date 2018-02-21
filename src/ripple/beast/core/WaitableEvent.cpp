//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.juce.com

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

#include <ripple/beast/core/WaitableEvent.h>
#include <cerrno>

#if BEAST_WINDOWS

#include <Windows.h>
#undef check
#undef direct
#undef max
#undef min
#undef TYPE_BOOL

namespace beast {

WaitableEvent::WaitableEvent (const bool manualReset, bool initiallySignaled)
    : handle (CreateEvent (0, manualReset ? TRUE : FALSE, initiallySignaled ? TRUE : FALSE, 0))
{
}

WaitableEvent::~WaitableEvent()
{
    CloseHandle (handle);
}

void WaitableEvent::signal() const
{
    SetEvent (handle);
}

void WaitableEvent::reset() const
{
    ResetEvent (handle);
}

bool WaitableEvent::wait () const
{
    return WaitForSingleObject (handle, INFINITE) == WAIT_OBJECT_0;
}

bool WaitableEvent::wait (const int timeOutMs) const
{
    if (timeOutMs >= 0)
        return WaitForSingleObject (handle,
            (DWORD) timeOutMs) == WAIT_OBJECT_0;
    return wait ();
}

}

#else

#include <sys/time.h>

namespace beast {

WaitableEvent::WaitableEvent (const bool useManualReset, bool initiallySignaled)
    : triggered (false), manualReset (useManualReset)
{
    pthread_cond_init (&condition, 0);

    pthread_mutexattr_t atts;
    pthread_mutexattr_init (&atts);
   #if ! BEAST_ANDROID
    pthread_mutexattr_setprotocol (&atts, PTHREAD_PRIO_INHERIT);
   #endif
    pthread_mutex_init (&mutex, &atts);

    if (initiallySignaled)
        signal ();
}

WaitableEvent::~WaitableEvent()
{
    pthread_cond_destroy (&condition);
    pthread_mutex_destroy (&mutex);
}

bool WaitableEvent::wait () const
{
    return wait (-1);
}

bool WaitableEvent::wait (const int timeOutMillisecs) const
{
    pthread_mutex_lock (&mutex);

    if (! triggered)
    {
        if (timeOutMillisecs < 0)
        {
            do
            {
                pthread_cond_wait (&condition, &mutex);
            }
            while (! triggered);
        }
        else
        {
            struct timeval now;
            gettimeofday (&now, 0);

            struct timespec time;
            time.tv_sec  = now.tv_sec  + (timeOutMillisecs / 1000);
            time.tv_nsec = (now.tv_usec + ((timeOutMillisecs % 1000) * 1000)) * 1000;

            if (time.tv_nsec >= 1000000000)
            {
                time.tv_nsec -= 1000000000;
                time.tv_sec++;
            }

            do
            {
                if (pthread_cond_timedwait (&condition, &mutex, &time) == ETIMEDOUT)
                {
                    pthread_mutex_unlock (&mutex);
                    return false;
                }
            }
            while (! triggered);
        }
    }

    if (! manualReset)
        triggered = false;

    pthread_mutex_unlock (&mutex);
    return true;
}

void WaitableEvent::signal() const
{
    pthread_mutex_lock (&mutex);
    triggered = true;
    pthread_cond_broadcast (&condition);
    pthread_mutex_unlock (&mutex);
}

void WaitableEvent::reset() const
{
    pthread_mutex_lock (&mutex);
    triggered = false;
    pthread_mutex_unlock (&mutex);
}

}

#endif
