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

#include <beast/threads/RecursiveMutex.h>

#if BEAST_WINDOWS

#include <Windows.h>
#undef check
#undef direct
#undef max
#undef min
#undef TYPE_BOOL

namespace beast {

RecursiveMutex::RecursiveMutex()
{
    // (just to check the MS haven't changed this structure and broken things...)
    static_assert (sizeof (CRITICAL_SECTION) <= sizeof (section), "");

    InitializeCriticalSection ((CRITICAL_SECTION*) section);
}

RecursiveMutex::~RecursiveMutex()
{
    DeleteCriticalSection ((CRITICAL_SECTION*) section);
}

void RecursiveMutex::lock() const
{
    EnterCriticalSection ((CRITICAL_SECTION*) section);
}

void RecursiveMutex::unlock() const
{
    LeaveCriticalSection ((CRITICAL_SECTION*) section);
}

bool RecursiveMutex::try_lock() const
{
    return TryEnterCriticalSection ((CRITICAL_SECTION*) section) != FALSE;
}

}

#else

namespace beast {

RecursiveMutex::RecursiveMutex()
{
    pthread_mutexattr_t atts;
    pthread_mutexattr_init (&atts);
    pthread_mutexattr_settype (&atts, PTHREAD_MUTEX_RECURSIVE);
#if ! BEAST_ANDROID
    pthread_mutexattr_setprotocol (&atts, PTHREAD_PRIO_INHERIT);
#endif
    pthread_mutex_init (&mutex, &atts);
    pthread_mutexattr_destroy (&atts);
}

RecursiveMutex::~RecursiveMutex()
{
    pthread_mutex_destroy (&mutex);
}

void RecursiveMutex::lock() const
{
    pthread_mutex_lock (&mutex);
}

void RecursiveMutex::unlock() const
{
    pthread_mutex_unlock (&mutex);
}

bool RecursiveMutex::try_lock() const
{
    return pthread_mutex_trylock (&mutex) == 0;
}

}

#endif
